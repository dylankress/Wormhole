//
// erasure.c
// Erasure coding integration — Reed-Solomon protection for chunk storage.
// by Dylan Kress
//

#include "erasure.h"
#include "chunk_store.h"
#include "wire_format.h"
#include <rs.h>
#include <blake3.h>
#include <string.h>
#include <stdio.h>

//=============================================================================
// Serialization format:
//   [1B k][1B m][4B stripe_count]
//   Per stripe:
//     [4B data_chunk_start][4B data_chunk_count]
//     m * ([32B parity_hash][4B parity_size])
//=============================================================================

#define EC_HEADER_SIZE (1 + 1 + 4)  // k + m + stripe_count
#define EC_STRIPE_FIXED (4 + 4)     // data_chunk_start + data_chunk_count
#define EC_PARITY_ENTRY (WH_HASH_SIZE + 4)  // hash + size per parity shard

//=============================================================================
// Encode
//=============================================================================

EC_GROUP *ErasureCoding_Encode(const FILE_MANIFEST *manifest, uint8_t k, uint8_t m)
{
    if (!manifest || k == 0 || m == 0) return NULL;
    if (manifest->chunk_count == 0) return NULL;

    // Create RS codec
    RS_CODEC *codec = RS_Create(k, m);
    if (!codec) return NULL;

    // Compute stripe count: ceil(chunk_count / k)
    uint32_t stripe_count = (manifest->chunk_count + k - 1) / k;

    EC_GROUP *group = (EC_GROUP *)calloc(1, sizeof(EC_GROUP));
    if (!group)
    {
        RS_Destroy(codec);
        return NULL;
    }

    group->k = k;
    group->m = m;
    group->stripe_count = stripe_count;
    group->stripes = (EC_STRIPE *)calloc(stripe_count, sizeof(EC_STRIPE));
    if (!group->stripes)
    {
        free(group);
        RS_Destroy(codec);
        return NULL;
    }

    // Allocate shard buffers (all WH_CHUNK_SIZE for uniform RS encoding)
    uint8_t **data_shards = (uint8_t **)malloc(k * sizeof(uint8_t *));
    uint8_t **parity_shards = (uint8_t **)malloc(m * sizeof(uint8_t *));
    if (!data_shards || !parity_shards)
    {
        free(data_shards);
        free(parity_shards);
        ErasureCoding_DestroyGroup(group);
        RS_Destroy(codec);
        return NULL;
    }

    for (uint8_t i = 0; i < k; i++)
    {
        data_shards[i] = (uint8_t *)malloc(WH_CHUNK_SIZE);
        if (!data_shards[i])
        {
            for (uint8_t j = 0; j < i; j++) free(data_shards[j]);
            free(data_shards);
            free(parity_shards);
            ErasureCoding_DestroyGroup(group);
            RS_Destroy(codec);
            return NULL;
        }
    }

    for (uint8_t i = 0; i < m; i++)
    {
        parity_shards[i] = (uint8_t *)malloc(WH_CHUNK_SIZE);
        if (!parity_shards[i])
        {
            for (uint8_t j = 0; j < i; j++) free(parity_shards[j]);
            for (uint8_t j = 0; j < k; j++) free(data_shards[j]);
            free(data_shards);
            free(parity_shards);
            ErasureCoding_DestroyGroup(group);
            RS_Destroy(codec);
            return NULL;
        }
    }

    // Process each stripe
    for (uint32_t s = 0; s < stripe_count; s++)
    {
        EC_STRIPE *stripe = &group->stripes[s];
        uint32_t start = s * k;
        uint32_t count = k;
        if (start + count > manifest->chunk_count)
        {
            count = manifest->chunk_count - start;
        }

        stripe->data_chunk_start = start;
        stripe->data_chunk_count = count;

        // Load data chunks from chunk store, zero-pad as needed
        for (uint8_t i = 0; i < k; i++)
        {
            memset(data_shards[i], 0, WH_CHUNK_SIZE);

            if (i < count)
            {
                uint32_t chunk_idx = start + i;
                uint32_t read_size = 0;
                if (!ChunkStore_Get(manifest->chunks[chunk_idx].hash,
                                     data_shards[i], &read_size))
                {
                    LOG_ERROR("[erasure] Failed to read chunk %u from store\n", chunk_idx);
                    goto cleanup_fail;
                }
                // Data is left-justified, rest is zero-padded from memset
            }
            // Else: padding shard is all zeros (already done by memset)
        }

        // Encode parity
        if (!RS_Encode(codec, (const uint8_t *const *)data_shards,
                        parity_shards, WH_CHUNK_SIZE))
        {
            LOG_ERROR("[erasure] RS_Encode failed for stripe %u\n", s);
            goto cleanup_fail;
        }

        // Hash and store each parity shard
        for (uint8_t p = 0; p < m; p++)
        {
            // Compute Blake3 hash
            blake3_hasher hasher;
            blake3_hasher_init(&hasher);
            blake3_hasher_update(&hasher, parity_shards[p], WH_CHUNK_SIZE);
            blake3_hasher_finalize(&hasher, stripe->parity_hashes[p], WH_HASH_SIZE);

            stripe->parity_sizes[p] = WH_CHUNK_SIZE;

            // Store parity chunk
            if (!ChunkStore_Put(stripe->parity_hashes[p],
                                 parity_shards[p], WH_CHUNK_SIZE))
            {
                LOG_ERROR("[erasure] Failed to store parity %u for stripe %u\n", p, s);
                goto cleanup_fail;
            }
        }
    }

    // Cleanup and return
    for (uint8_t i = 0; i < k; i++) free(data_shards[i]);
    for (uint8_t i = 0; i < m; i++) free(parity_shards[i]);
    free(data_shards);
    free(parity_shards);
    RS_Destroy(codec);
    return group;

cleanup_fail:
    for (uint8_t i = 0; i < k; i++) free(data_shards[i]);
    for (uint8_t i = 0; i < m; i++) free(parity_shards[i]);
    free(data_shards);
    free(parity_shards);
    ErasureCoding_DestroyGroup(group);
    RS_Destroy(codec);
    return NULL;
}

//=============================================================================
// Reconstruct
//=============================================================================

BOOLEAN ErasureCoding_ReconstructChunk(uint32_t chunk_index, const EC_GROUP *group,
                                        const FILE_MANIFEST *manifest)
{
    if (!group || !manifest) return FALSE;
    if (chunk_index >= manifest->chunk_count) return FALSE;

    uint8_t k = group->k;
    uint8_t m = group->m;
    uint8_t n = k + m;

    // Find which stripe contains this chunk
    uint32_t stripe_idx = chunk_index / k;
    if (stripe_idx >= group->stripe_count) return FALSE;

    const EC_STRIPE *stripe = &group->stripes[stripe_idx];

    // Create RS codec
    RS_CODEC *codec = RS_Create(k, m);
    if (!codec) return FALSE;

    // Allocate shard array and present flags
    uint8_t **shards = (uint8_t **)calloc(n, sizeof(uint8_t *));
    uint8_t *present = (uint8_t *)calloc(n, 1);
    if (!shards || !present)
    {
        free(shards);
        free(present);
        RS_Destroy(codec);
        return FALSE;
    }

    // Allocate all shard buffers
    for (uint8_t i = 0; i < n; i++)
    {
        shards[i] = (uint8_t *)calloc(1, WH_CHUNK_SIZE);
        if (!shards[i])
        {
            for (uint8_t j = 0; j < i; j++) free(shards[j]);
            free(shards);
            free(present);
            RS_Destroy(codec);
            return FALSE;
        }
    }

    // Load data shards
    for (uint8_t i = 0; i < k; i++)
    {
        if (i < stripe->data_chunk_count)
        {
            uint32_t ci = stripe->data_chunk_start + i;
            uint32_t read_size = 0;
            if (ChunkStore_Get(manifest->chunks[ci].hash, shards[i], &read_size))
            {
                present[i] = 1;
            }
            // If missing, present[i] stays 0, buffer stays zero
        }
        else
        {
            // Padding shard (all zeros) — always "present"
            present[i] = 1;
        }
    }

    // Load parity shards
    for (uint8_t p = 0; p < m; p++)
    {
        uint32_t read_size = 0;
        if (ChunkStore_Get(stripe->parity_hashes[p], shards[k + p], &read_size))
        {
            present[k + p] = 1;
        }
    }

    // Count available
    int available = 0;
    for (uint8_t i = 0; i < n; i++)
    {
        if (present[i]) available++;
    }

    if (available < k)
    {
        // Not enough shards to reconstruct
        for (uint8_t i = 0; i < n; i++) free(shards[i]);
        free(shards);
        free(present);
        RS_Destroy(codec);
        return FALSE;
    }

    // Decode
    if (!RS_Decode(codec, shards, present, WH_CHUNK_SIZE))
    {
        for (uint8_t i = 0; i < n; i++) free(shards[i]);
        free(shards);
        free(present);
        RS_Destroy(codec);
        return FALSE;
    }

    // Store the reconstructed chunk
    uint8_t local_idx = (uint8_t)(chunk_index - stripe->data_chunk_start);
    uint32_t actual_size = manifest->chunks[chunk_index].chunk_size;

    if (!ChunkStore_Put(manifest->chunks[chunk_index].hash,
                         shards[local_idx], actual_size))
    {
        for (uint8_t i = 0; i < n; i++) free(shards[i]);
        free(shards);
        free(present);
        RS_Destroy(codec);
        return FALSE;
    }

    for (uint8_t i = 0; i < n; i++) free(shards[i]);
    free(shards);
    free(present);
    RS_Destroy(codec);
    return TRUE;
}

//=============================================================================
// Regenerate parity for a single stripe
//=============================================================================

BOOLEAN ErasureCoding_RegenerateStripeParity(uint32_t stripe_idx,
                                              EC_GROUP *group,
                                              const FILE_MANIFEST *manifest)
{
    if (!group || !manifest) return FALSE;
    if (stripe_idx >= group->stripe_count) return FALSE;

    uint8_t k = group->k;
    uint8_t m = group->m;
    EC_STRIPE *stripe = &group->stripes[stripe_idx];

    // Create RS codec
    RS_CODEC *codec = RS_Create(k, m);
    if (!codec) return FALSE;

    // Allocate shard buffers
    uint8_t **data_shards = (uint8_t **)malloc(k * sizeof(uint8_t *));
    uint8_t **parity_shards = (uint8_t **)malloc(m * sizeof(uint8_t *));
    if (!data_shards || !parity_shards)
    {
        free(data_shards);
        free(parity_shards);
        RS_Destroy(codec);
        return FALSE;
    }

    for (uint8_t i = 0; i < k; i++)
    {
        data_shards[i] = (uint8_t *)calloc(1, WH_CHUNK_SIZE);
        if (!data_shards[i])
        {
            for (uint8_t j = 0; j < i; j++) free(data_shards[j]);
            free(data_shards);
            free(parity_shards);
            RS_Destroy(codec);
            return FALSE;
        }
    }

    for (uint8_t i = 0; i < m; i++)
    {
        parity_shards[i] = (uint8_t *)malloc(WH_CHUNK_SIZE);
        if (!parity_shards[i])
        {
            for (uint8_t j = 0; j < i; j++) free(parity_shards[j]);
            for (uint8_t j = 0; j < k; j++) free(data_shards[j]);
            free(data_shards);
            free(parity_shards);
            RS_Destroy(codec);
            return FALSE;
        }
    }

    // Load data chunks — all must be present
    for (uint8_t i = 0; i < k; i++)
    {
        if (i < stripe->data_chunk_count)
        {
            uint32_t ci = stripe->data_chunk_start + i;
            uint32_t read_size = 0;
            if (!ChunkStore_Get(manifest->chunks[ci].hash, data_shards[i], &read_size))
            {
                LOG_ERROR("[erasure] Parity regen: missing data chunk %u\n", ci);
                goto cleanup_fail;
            }
        }
        // Else: padding shard stays zero (from calloc)
    }

    // Encode parity
    if (!RS_Encode(codec, (const uint8_t *const *)data_shards,
                    parity_shards, WH_CHUNK_SIZE))
    {
        LOG_ERROR("[erasure] RS_Encode failed for parity regen stripe %u\n", stripe_idx);
        goto cleanup_fail;
    }

    // Hash and store each parity shard, update stripe metadata
    for (uint8_t p = 0; p < m; p++)
    {
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);
        blake3_hasher_update(&hasher, parity_shards[p], WH_CHUNK_SIZE);
        blake3_hasher_finalize(&hasher, stripe->parity_hashes[p], WH_HASH_SIZE);

        stripe->parity_sizes[p] = WH_CHUNK_SIZE;

        if (!ChunkStore_Put(stripe->parity_hashes[p],
                             parity_shards[p], WH_CHUNK_SIZE))
        {
            LOG_ERROR("[erasure] Failed to store regen parity %u for stripe %u\n", p, stripe_idx);
            goto cleanup_fail;
        }
    }

    for (uint8_t i = 0; i < k; i++) free(data_shards[i]);
    for (uint8_t i = 0; i < m; i++) free(parity_shards[i]);
    free(data_shards);
    free(parity_shards);
    RS_Destroy(codec);
    return TRUE;

cleanup_fail:
    for (uint8_t i = 0; i < k; i++) free(data_shards[i]);
    for (uint8_t i = 0; i < m; i++) free(parity_shards[i]);
    free(data_shards);
    free(parity_shards);
    RS_Destroy(codec);
    return FALSE;
}

//=============================================================================
// Serialization
//=============================================================================

uint8_t *ErasureCoding_SerializeGroup(const EC_GROUP *group, size_t *out_size)
{
    if (!group || !out_size) return NULL;

    size_t per_stripe = EC_STRIPE_FIXED + (size_t)group->m * EC_PARITY_ENTRY;
    size_t total = EC_HEADER_SIZE + (size_t)group->stripe_count * per_stripe;

    uint8_t *buf = (uint8_t *)malloc(total);
    if (!buf) return NULL;

    uint8_t *p = buf;

    // Header
    *p++ = group->k;
    *p++ = group->m;
    WriteUint32LE(p, group->stripe_count); p += 4;

    // Stripes
    for (uint32_t s = 0; s < group->stripe_count; s++)
    {
        const EC_STRIPE *stripe = &group->stripes[s];
        WriteUint32LE(p, stripe->data_chunk_start); p += 4;
        WriteUint32LE(p, stripe->data_chunk_count); p += 4;

        for (uint8_t i = 0; i < group->m; i++)
        {
            memcpy(p, stripe->parity_hashes[i], WH_HASH_SIZE);
            p += WH_HASH_SIZE;
            WriteUint32LE(p, stripe->parity_sizes[i]);
            p += 4;
        }
    }

    *out_size = total;
    return buf;
}

EC_GROUP *ErasureCoding_DeserializeGroup(const uint8_t *data, size_t size)
{
    if (!data || size < EC_HEADER_SIZE) return NULL;

    const uint8_t *p = data;

    uint8_t k = *p++;
    uint8_t m = *p++;
    uint32_t stripe_count = ReadUint32LE(p); p += 4;

    if (k == 0 || m == 0 || m > EC_MAX_PARITY) return NULL;

    // Validate buffer size
    size_t per_stripe = EC_STRIPE_FIXED + (size_t)m * EC_PARITY_ENTRY;
    size_t expected = EC_HEADER_SIZE + (size_t)stripe_count * per_stripe;
    if (size < expected) return NULL;

    EC_GROUP *group = (EC_GROUP *)calloc(1, sizeof(EC_GROUP));
    if (!group) return NULL;

    group->k = k;
    group->m = m;
    group->stripe_count = stripe_count;

    if (stripe_count > 0)
    {
        group->stripes = (EC_STRIPE *)calloc(stripe_count, sizeof(EC_STRIPE));
        if (!group->stripes)
        {
            free(group);
            return NULL;
        }

        for (uint32_t s = 0; s < stripe_count; s++)
        {
            EC_STRIPE *stripe = &group->stripes[s];
            stripe->data_chunk_start = ReadUint32LE(p); p += 4;
            stripe->data_chunk_count = ReadUint32LE(p); p += 4;

            for (uint8_t i = 0; i < m; i++)
            {
                memcpy(stripe->parity_hashes[i], p, WH_HASH_SIZE);
                p += WH_HASH_SIZE;
                stripe->parity_sizes[i] = ReadUint32LE(p);
                p += 4;
            }
        }
    }

    return group;
}

//=============================================================================
// Destroy
//=============================================================================

void ErasureCoding_DestroyGroup(EC_GROUP *group)
{
    if (!group) return;
    free(group->stripes);
    free(group);
}

//=============================================================================
// Metadata file I/O (EC_GROUP + manifest combined)
// Format: [4B ec_size][ec_data][4B manifest_size][manifest_data]
//=============================================================================

BOOLEAN ErasureCoding_SaveMetadata(const char *path, const EC_GROUP *group,
                                     const FILE_MANIFEST *manifest)
{
    if (!path || !group || !manifest) return FALSE;

    size_t ec_size = 0;
    uint8_t *ec_buf = ErasureCoding_SerializeGroup(group, &ec_size);
    if (!ec_buf) return FALSE;

    size_t man_size = 0;
    uint8_t *man_buf = Manifest_Serialize(manifest, &man_size);
    if (!man_buf)
    {
        free(ec_buf);
        return FALSE;
    }

    FILE *fp = fopen(path, "wb");
    if (!fp)
    {
        free(ec_buf);
        free(man_buf);
        return FALSE;
    }

    uint8_t len_buf[4];
    BOOLEAN ok = TRUE;

    // Write EC group
    WriteUint32LE(len_buf, (uint32_t)ec_size);
    if (fwrite(len_buf, 1, 4, fp) != 4) ok = FALSE;
    if (ok && fwrite(ec_buf, 1, ec_size, fp) != ec_size) ok = FALSE;

    // Write manifest
    WriteUint32LE(len_buf, (uint32_t)man_size);
    if (ok && fwrite(len_buf, 1, 4, fp) != 4) ok = FALSE;
    if (ok && fwrite(man_buf, 1, man_size, fp) != man_size) ok = FALSE;

    fclose(fp);
    free(ec_buf);
    free(man_buf);
    return ok;
}

EC_GROUP *ErasureCoding_LoadMetadata(const char *path, FILE_MANIFEST **out_manifest)
{
    if (!path || !out_manifest) return NULL;
    *out_manifest = NULL;

    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    // Get file size
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size < 8)  // minimum: 4B ec_size + 4B manifest_size
    {
        fclose(fp);
        return NULL;
    }

    uint8_t *file_buf = (uint8_t *)malloc((size_t)file_size);
    if (!file_buf)
    {
        fclose(fp);
        return NULL;
    }

    if (fread(file_buf, 1, (size_t)file_size, fp) != (size_t)file_size)
    {
        free(file_buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);

    const uint8_t *p = file_buf;
    size_t remaining = (size_t)file_size;

    // Read EC group
    if (remaining < 4) { free(file_buf); return NULL; }
    uint32_t ec_size = ReadUint32LE(p);
    p += 4; remaining -= 4;

    if (remaining < ec_size) { free(file_buf); return NULL; }
    EC_GROUP *group = ErasureCoding_DeserializeGroup(p, ec_size);
    p += ec_size; remaining -= ec_size;

    if (!group) { free(file_buf); return NULL; }

    // Read manifest
    if (remaining < 4) { free(file_buf); ErasureCoding_DestroyGroup(group); return NULL; }
    uint32_t man_size = ReadUint32LE(p);
    p += 4; remaining -= 4;

    if (remaining < man_size) { free(file_buf); ErasureCoding_DestroyGroup(group); return NULL; }
    FILE_MANIFEST *manifest = Manifest_Deserialize(p, man_size);

    free(file_buf);

    if (!manifest)
    {
        ErasureCoding_DestroyGroup(group);
        return NULL;
    }

    *out_manifest = manifest;
    return group;
}
